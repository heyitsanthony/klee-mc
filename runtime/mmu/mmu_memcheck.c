/**
 * implementation of heap-tracking mmu deal.
 * looks for accesses to heap data */
#include "klee/klee.h"
#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"
#include "list.h"
#include "mmu.h"

static int malloc_c = 0;
static int free_c = 0;

struct heap_ent
{
	void		*he_base;
	unsigned	 he_len;
	struct list_item he_li;
};

#define HEAP_BUCKETS	512
#define PTR_TO_IDX(x)	((((uint64_t)x) >> 4) % HEAP_BUCKETS)


struct list heap_list;


void post_int_free(int64_t retval)
{
	struct list_item	*li;
	void	*free_addr = (void*)retval;


	list_for_all(&heap_list, li) {
		struct heap_ent	*he = list_get_data(&heap_list, li);
		if (he->he_base == free_addr) {
			list_remove(li);
			break;
		}
	}

	klee_print_expr("exiting free", retval);
	free_c++;
}

void post__int_malloc(int64_t aux)
{
	void			*regs;
	uint64_t		ret_v;
	struct heap_ent		*he;

	klee_print_expr("exiting malloc", aux);
	regs = kmc_regs_get();

	malloc_c++; 

	he = malloc(sizeof(*he));
	he->he_len = aux;
	he->he_base = (void*)GET_RET(regs);

	klee_print_expr("ADD TO HEAP LIST", heap_list.lst_list.li_next);

	list_add_head(&heap_list, &he->he_li);

	klee_print_expr("malloced", (long)he->he_base);
}

void __hookpre__int_free(void* regfile)
{
	klee_print_expr("entering free", 0);
	klee_hook_return(1, &post_int_free, GET_ARG0(regfile));
}

void __hookpre__int_malloc(void* regfile)
{
	klee_print_expr("entering malloc", 0);
	klee_hook_return(1, &post__int_malloc, GET_ARG0(regfile));
}

void mmu_cleanup_memcheck(void)
{
	klee_print_expr("seen free", free_c);
	klee_print_expr("seen malloc", malloc_c);
}

void mmu_init_memcheck(void)
{
	klee_print_expr("INIT MEMCHECK", 123);
	list_init(&heap_list, offsetof(struct heap_ent, he_li)); }

#define MMU_LOADC(x,y)			\
y mmu_load_##x##_memcheckc(void* addr)	\
{ return mmu_load_##x##_cnulltlb(addr); }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_memcheckc(void* addr, y v)	\
{ mmu_store_##x##_cnulltlb(addr, v); }	\

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_memcheck(void* addr)	\
{ return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_memcheck(void* addr, y v)	\
{ mmu_store_##x##_objwide(addr, v); }		\



#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

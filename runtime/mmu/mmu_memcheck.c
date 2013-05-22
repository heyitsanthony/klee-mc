/**
 * implementation of standard memcheck algorithm.
 * looks for accesses to heap data
 */
#include "klee/klee.h"
#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"
#include "shadow.h"
#include "list.h"
#include "mmu.h"

static int malloc_c = 0;
static int free_c = 0;
static uint8_t in_sym_mmu = 0;
static uint8_t in_heap = 0;

struct heap_ent
{
	void		*he_base;
	unsigned	 he_len;
	struct list_item he_li;
};

#define HEAP_BUCKETS	512
#define PTR_TO_IDX(x)	((((uint64_t)x) >> 4) % HEAP_BUCKETS)
#define HEAP_GRAN_BYTES	16	/* 16 byte chunks because of sse instructions */
#define HEAP_FLAG_BITS	2

// used to track non-reentrant allocation functions which modify
// reserved data
#define HEAP_ENTER	in_heap |= 1;
#define HEAP_LEAVE	in_heap &= ~1;

// there's some funny business with the way malloc is optimized--
// sometimes I see two enters followed by only one exit. It's possible that
// there's a goto to the start of the function which is causing it to think
// there are two calls instead of one call and a jump. Fix?
//#define HEAP_ENTER	in_heap++;
//#define HEAP_LEAVE	in_heap--;


#define ROUND_BYTES2UNITS(x)	((((x) + HEAP_GRAN_BYTES-1)/HEAP_GRAN_BYTES))
#define NEXT_CHUNK(x)		HEAP_GRAN_BYTES*ROUND_BYTES2UNITS(x)

/* two bits only!! */
#define SH_FL_UNINIT		0
#define SH_FL_ALLOC		1
#define SH_FL_FREE		2
#define SH_FL_UNDEF		3	/* don't use or get_range breaks */

struct list		heap_l;
struct shadow_info	heap_si;

static uint64_t shadow_get_range(uint64_t a, unsigned n)
{
	uint64_t	v = 0;
	unsigned	i;
	for (i = 0; i < n; i += HEAP_GRAN_BYTES)
		v |= shadow_get(&heap_si, a+i);
	return v;
}

void post_int_free(int64_t retval)
{
	struct list_item	*li;
	struct heap_ent		*he;
	void			*free_addr = (void*)retval;

	HEAP_LEAVE

	list_for_all(&heap_l, li) {
		he = list_get_data(&heap_l, li);
		if (he->he_base == free_addr) {
			list_remove(li);
			break;
		}
		he = NULL;
	}

	if (he == NULL)
		return;

	free_c++;

	klee_tlb_invalidate(he->he_base, 4096);

	klee_print_expr("[memcheck] freed bytes", he->he_len);
	shadow_put_units_range(
		&heap_si,
		(long)he->he_base,
		SH_FL_FREE,
		ROUND_BYTES2UNITS(he->he_len));
	free(he);
}

void __hookpre___GI___libc_free(void* regfile)
{
	void	*ptr;

	ptr = GET_ARG0_PTR(regfile);

	/* ignore free(NULL)s-- nop */
	if (ptr == NULL) 
		return;

	HEAP_ENTER


	klee_print_expr("[memcheck] freeing", (long)ptr);

	/* disable header boundary so free doesn't cause an error */
	shadow_put(&heap_si, (long)ptr-1, SH_FL_UNINIT);
	klee_hook_return(1, &post_int_free, GET_ARG0(regfile));
}

static struct heap_ent* add_heap_ent(void* base, unsigned len)
{
	struct heap_ent		*he;

	he = malloc(sizeof(*he));
	he->he_len = len;
	he->he_base = base;
	list_add_head(&heap_l, &he->he_li);

	return he;
}

void post__int_malloc(int64_t aux)
{
	void			*regs;
	struct heap_ent		*he;
	int			bound_l, bound_r;

	regs = kmc_regs_get();

	malloc_c++;

	/* malloc doesn't always succeed */
	if (!GET_RET(regs))
		goto done;

	he = add_heap_ent((void*)GET_RET(regs), aux);

	klee_tlb_invalidate(he->he_base, 4096);

	bound_l = shadow_get(&heap_si, (long)he->he_base - 1);
	bound_r = shadow_get(&heap_si, NEXT_CHUNK((long)he->he_base + he->he_len));

#if 0
	/* TODO: scan for overlapping segments */
	int n = shadow_get_range((long)he->he_base, he->he_len);
	klee_print_expr("[memcheck] flag", n);
#endif

	shadow_put_units_range(
		&heap_si,
		(long)he->he_base,
		SH_FL_ALLOC,
		ROUND_BYTES2UNITS(he->he_len));

	/* guard boundaries */
	if (bound_l == SH_FL_UNINIT)
		shadow_put(
			&heap_si,
			(long)he->he_base - 1,
			SH_FL_FREE);

	if (bound_r == SH_FL_UNINIT)
		shadow_put(
			&heap_si,
			NEXT_CHUNK((long)he->he_base + he->he_len),
			SH_FL_FREE);
	
	klee_print_expr("[memcheck] malloc", (long)he->he_base);
	klee_print_expr("[memcheck] size", he->he_len);

done:
	HEAP_LEAVE
}

/* reserved data manipulation only happens once int_malloc is called */
void __hookpre__int_malloc(void* regfile) { HEAP_ENTER }

void __hookpre___GI___libc_malloc(void* regfile)
{
	klee_print_expr("[memcheck] malloc enter", GET_ARG0(regfile));
	klee_hook_return(1, &post__int_malloc, GET_ARG0(regfile));
}

void __hookpre___GI___libc_realloc(void* regfile)
{
	klee_print_expr("[memcheck] realloc enter", GET_ARG1(regfile));
	klee_hook_return(1, &post__int_malloc, GET_ARG1(regfile));
}

void __hookpre___calloc(void* regfile)
{
	klee_print_expr("[memcheck] calloc enter", GET_ARG1(regfile));
	klee_hook_return(
		1, &post__int_malloc, GET_ARG0(regfile) * GET_ARG1(regfile));
}

void __hookpre___GI___libc_memalign(void* regfile)
{
	klee_print_expr("[memcheck] memalign enter", GET_ARG1(regfile));
	klee_hook_return(1, &post__int_malloc, GET_ARG1(regfile));
}

// any reason to do cleanups?
// void mmu_cleanup_memcheck(void) { }

void mmu_init_memcheck(void)
{
	list_init(&heap_l, offsetof(struct heap_ent, he_li));
	shadow_init(&heap_si, HEAP_GRAN_BYTES, HEAP_FLAG_BITS, SH_FL_UNINIT);
}

#define MMU_LOADC(x,y)				\
y mmu_load_##x##_memcheckc(void* addr)		\
{						\
if (!shadow_pg_used(&heap_si, (uint64_t)addr))	\
	return mmu_load_##x##_cnulltlb(addr);	\
if (!in_heap && (shadow_get_range((uint64_t)addr, x/8) & SH_FL_FREE))	\
	klee_uerror("Loading from free const pointer", "heap.err");	\
return mmu_load_##x##_cnull(addr); }


#define MMU_STOREC(x,y)					\
void mmu_store_##x##_memcheckc(void* addr, y v)		\
{							\
if (!shadow_pg_used(&heap_si, (uint64_t)addr)) {	\
	mmu_store_##x##_cnulltlb(addr, v);		\
	return;						\
}							\
if (!in_heap && (shadow_get_range((uint64_t)addr, x/8) & SH_FL_FREE)) {	\
	klee_print_expr("Bad store ptr", addr);				\
	klee_uerror("Storing to free const pointer", "heap.err");	\
}									\
mmu_store_##x##_cnulltlb(addr, v); }


#define MMU_LOAD(x,y)			\
y mmu_load_##x##_memcheck(void* addr)	\
{					\
if (!in_sym_mmu) {			\
in_sym_mmu++;				\
if (!in_heap && (shadow_get_range((uint64_t)addr, x/8) & SH_FL_FREE))	\
	klee_uerror("Loading from free sym pointer", "heap.err");	\
in_sym_mmu--;	\
}	\
return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_memcheck(void* addr, y v)	\
{ \
if (!in_sym_mmu) {	\
in_sym_mmu++;	\
if (!in_heap && (shadow_get_range((uint64_t)addr, x/8) & SH_FL_FREE))	\
	klee_uerror("Storing to free sym pointer", "heap.err");	\
in_sym_mmu--;	\
}	\
mmu_store_##x##_objwide(addr, v); }

#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

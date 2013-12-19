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

/* does consistency checks on shadow memory SLOW */
//#define PARANOIA_DEBUG
//#define USE_GUARDS

static int malloc_c = 0;
static int free_c = 0;
static uint8_t in_sym_mmu = 0;
static uint8_t in_heap = 0;
static uint8_t from_calloc = 0;

struct heap_ent
{
	void		*he_base;
	unsigned	 he_len;
	struct list_item he_li;
};

#define GET_HT_IDX(x)	(((uint64_t)x / 16) & 0xff)
#define GET_HEAP_L(x)	(&heap_tab.ht_l[GET_HT_IDX(x)])
#define HEAP_TAB_LISTS	(4096/16)
struct heap_table { struct list	ht_l[HEAP_TAB_LISTS]; };

#define REGPARAM	void* regfile

//#define HEAP_BUCKETS	512
//#define PTR_TO_IDX(x)	((((uint64_t)x) >> 4) % HEAP_BUCKETS)

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


#define ROUND_BYTES2UNITS(a,x)	\
	((	(((long)(a))&(HEAP_GRAN_BYTES-1)) +	\
		(x)+HEAP_GRAN_BYTES-1)/HEAP_GRAN_BYTES)

#define NEXT_CHUNK(x)		(HEAP_GRAN_BYTES*ROUND_BYTES2UNITS(0, x))

/* two bits only!! */
#define SH_FL_UNINIT		0
#define SH_FL_ALLOC		1
#define SH_FL_FREE		2
#define SH_FL_UNDEF		3	/* don't use or get_range breaks */

#define SH_EX_FREE	(1 << SH_FL_FREE)
#define SH_EX_ALLOC	(1 << SH_FL_ALLOC)

#define check_mix_r(a, x, af, am)	\
do {	\
	uint64_t	v;		\
\
	v = shadow_get_range_explode((uint64_t)(a), x);	\
	v &= (SH_EX_FREE | SH_EX_ALLOC);		\
	if (!klee_prefer_ne(v, 0)) break;		\
	if (v & SH_EX_FREE) {				\
		HEAP_REPORT_W(af, a, x);		\
	} else if (v & SH_EX_ALLOC) {			\
		HEAP_REPORT_W(am, a, x);		\
	}						\
} while (0)

#define check_mix_w(a, x, af)	\
do {	\
	uint64_t	v;	\
\
	v = shadow_get_range_explode((uint64_t)(a), x);	\
	v &= (SH_EX_FREE | SH_EX_ALLOC);		\
	if (!klee_prefer_ne(v, 0)) break;		\
	if (v == SH_EX_FREE) {				\
		HEAP_REPORT_W(af, a, x);		\
	} else if (v == SH_EX_ALLOC) {			\
		shadow_put_units_range(			\
			&heap_si,			\
			(long)(a),			\
			SH_FL_UNINIT,			\
			ROUND_BYTES2UNITS(a, x));	\
	}						\
} while (0)

#define check_r(a,x,m,n) check_mix_r(a,x, m " free " n, m " fresh " n)
#define check_w(a,x,m,n) check_mix_w(a,x, m " free " n)

#define extent_has_free(x,y)	\
	(shadow_get_range_explode((uint64_t)x, y) & SH_EX_FREE)

static struct heap_table	heap_tab;
static struct shadow_info	heap_si;

static struct kreport_ent	kr_tab[] =
{ MK_KREPORT("address"), MK_KREPORT("bytes"), MK_KREPORT(NULL) };

#define HEAP_REPORT(msg, a) HEAP_REPORT_W(msg, a, 0)

#define HEAP_REPORT_W(msg, a, w)	\
do {					\
	SET_KREPORT(&kr_tab[0], a);	\
	SET_KREPORT(&kr_tab[1], w);	\
	klee_ureport_details(msg, "heap.err", &kr_tab);	\
} while (0)


static uint64_t shadow_get_range_explode(uint64_t a, unsigned byte_c)
{
	uint64_t	v = 0;
	unsigned	i;
	for (i = 0; i < byte_c; i += HEAP_GRAN_BYTES)
		v |= (1 << shadow_get(&heap_si, a+i));
	return v;
}

struct heap_ent* take_he(void* addr)
{
	struct list		*hl;
	struct list_item	*li;
	struct heap_ent		*he;

	hl = GET_HEAP_L(addr);
	list_for_all(hl, li) {
		he = list_get_data(hl, li);
		if (he->he_base == addr) {
			list_remove(li);
			break;
		}
		he = NULL;
	}

	return he;
}

void post_int_free(int64_t fa)
{
	struct heap_ent	*he;
	void		*free_addr = (void*)fa;

	HEAP_LEAVE

	he = take_he(free_addr);
	if (he == NULL) {
		/* heap and marked free? must be double free */
		if (extent_has_free(free_addr, 1))
			HEAP_REPORT("Freeing freed pointer!", fa);
		return;
	}

	free_c++;

	klee_tlb_invalidate(he->he_base, 4096);

	klee_print_expr("[memcheck] freed bytes", he->he_len);
	shadow_put_units_range(
		&heap_si,
		(long)he->he_base,
		SH_FL_FREE,
		ROUND_BYTES2UNITS(he->he_base, he->he_len));
	// klee_assert (extent_has_free(he->he_base, he->he_len));

	free(he);
}


void __hookpre___GI___libc_free(REGPARAM)
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
	list_add_head(GET_HEAP_L(base), &he->he_li);

	return he;
}

struct heap_ent* post_alloc(int64_t aux)
{
	void			*regs;
	struct heap_ent		*he;

	regs = kmc_regs_get();

	malloc_c++;

	/* malloc doesn't always succeed */
	if (!GET_RET(regs))
		return NULL;

	he = add_heap_ent((void*)GET_RET(regs), aux);

	klee_tlb_invalidate(he->he_base, 4096);

	/* guard boundaries */
	/* XXX: this is bad because; what if allocate prior to taking
	 * snapshot and covers boundary? Bad memcheck */

#if USE_GUARDS
	int	bound_l, bound_r;
	bound_l = shadow_get(&heap_si, (long)he->he_base - 1);
	bound_r = shadow_get(&heap_si,NEXT_CHUNK((long)he->he_base+he->he_len));
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
#endif

#if 0
	/* TODO: scan for overlapping segments */
	int n = shadow_get_range((long)he->he_base, he->he_len);
	klee_print_expr("[memcheck] flag", n);
#endif
	return he;
}

void post__calloc(int64_t aux)
{
	struct heap_ent	*he;

	he = post_alloc(aux);
	if (he == NULL) goto done;

	/* do not yell about accesses.. initialized by calloc */
	shadow_put_units_range(
		&heap_si,
		(long)he->he_base,
		SH_FL_UNINIT,
		ROUND_BYTES2UNITS(he->he_base, he->he_len));
	
	klee_assert (!extent_has_free(he->he_base, he->he_len));
	klee_print_expr("[memcheck] calloced addr", (long)he->he_base);
	klee_print_expr("[memcheck] calloced size", he->he_len);
done:
	from_calloc = 0;
	HEAP_LEAVE
}

void post__int_malloc(int64_t aux)
{
	struct heap_ent	*he;

	he = post_alloc(aux);
	if (he == NULL) goto done;

	/* there appears to be a reentrancy issue here;
	 * force in-heap status so shadow_put does not trigger 
	 * reentrant behavior on shadow memory */
	HEAP_ENTER

	shadow_put_units_range(
		&heap_si,
		(long)he->he_base,
		SH_FL_ALLOC,
		ROUND_BYTES2UNITS(he->he_base, he->he_len));

	klee_print_expr("[memcheck] malloc", (long)he->he_base);
	klee_print_expr("[memcheck] size", he->he_len);

done:
	HEAP_LEAVE
}

void post__realloc(int64_t aux)
{
	void		*ret_addr;
	struct heap_ent	*he;

	ret_addr = (void*)(GET_RET(kmc_regs_get()));
	if (ret_addr == NULL)
		goto done;

	he = take_he(ret_addr);
	if (he == NULL) {
		/* allocated something new */
		post__calloc(aux);
		return;
	}

	long	n = aux;

#ifdef PARANOIA_DEBUG
	klee_assert (!extent_has_free(he->he_base, he->he_len));
#endif
	if (he->he_len < aux) {
		void	*next = (void*)(NEXT_CHUNK((long)he->he_base+he->he_len));
		/* expanding, mark new space as ALLOC */
		n -= he->he_len;
		n -= (long)next - ((long)he->he_base+he->he_len);
		if (n > 0) {
			shadow_put_units_range(
				&heap_si,
				(long)next,
				SH_FL_ALLOC,
				ROUND_BYTES2UNITS(next, n));
		}
		klee_print_expr("[memcheck] realloc expanding", ret_addr);
		klee_print_expr("[memcheck] new start", next);
		klee_print_expr("[memcheck] new len", n);
#ifdef PARANOIA_DEBUG
		klee_assert (!extent_has_free(he->he_base, he->he_len));
#endif
	} else if (he->he_len > aux) {
		/* chunk that follows last ... */
		void	*next = (void*)(NEXT_CHUNK((long)he->he_base+aux));
		/* shrinking, mark old space as FREE */
		n = (long)next - ((long)he->he_base + aux); // slack
		n = he->he_len - aux - n; // free bytes from start of next block
		klee_print_expr("[memcheck] realloc shrinking", ret_addr);

		klee_print_expr("[memcheck] realloc shrinking start", next);
		klee_print_expr("[memcheck] realloc shrinking bytes", n);
		if (n > 0) {
			shadow_put_units_range(
				&heap_si,
				(long)next,
				SH_FL_FREE,
				ROUND_BYTES2UNITS(next, n));
		}
	} else {
		klee_print_expr("[memcheck] realloc no change", ret_addr);
	}

	he->he_len = aux;

	/* restore entry to heap list */
	list_add_head(GET_HEAP_L(he->he_base), &he->he_li);
done:
	HEAP_LEAVE
}

/* reserved data manipulation only happens once int_malloc is called */
void __hookpre__int_malloc(REGPARAM) { HEAP_ENTER }
void __hookpre__int_realloc(REGPARAM) { HEAP_ENTER }

void __hookpre___GI___libc_malloc(REGPARAM)
{
	klee_print_expr("[memcheck] malloc enter", GET_ARG0(regfile));
	if (from_calloc) return;
	klee_hook_return(1, &post__int_malloc, GET_ARG0(regfile));
}

void __hookpre___GI___libc_realloc(REGPARAM)
{
	HEAP_ENTER
	klee_print_expr("[memcheck] realloc enter", GET_ARG1(regfile));
	from_calloc++;
	klee_hook_return(1, &post__realloc, GET_ARG1(regfile));
}

void post_malloc_usable_size(int64_t v) { HEAP_LEAVE; }

void __hookpre___malloc_usable_size(REGPARAM)
{
	HEAP_ENTER
	klee_hook_return(1, &post_malloc_usable_size, GET_ARG0(regfile));
}

void __hookpre___calloc(REGPARAM)
{
	HEAP_ENTER
	from_calloc++;
	klee_print_expr("[memcheck] calloc enter",
		GET_ARG1(regfile) * GET_ARG0(regfile));
	klee_hook_return(
		1, &post__calloc, GET_ARG0(regfile) * GET_ARG1(regfile));
}

void __hookpre___GI___libc_memalign(REGPARAM)
{
	HEAP_ENTER
	klee_print_expr("[memcheck] memalign enter", GET_ARG1(regfile));
	klee_hook_return(1, &post__int_malloc, GET_ARG1(regfile));
}

// any reason to do cleanups?
// void mmu_cleanup_memcheck(void) { }

void mmu_init_memcheck(void)
{
	HEAP_ENTER

	for (unsigned i = 0; i < HEAP_TAB_LISTS; i++) {
		list_init(
			&heap_tab.ht_l[i],
			offsetof(struct heap_ent, he_li));
	}

	shadow_init(&heap_si, HEAP_GRAN_BYTES, HEAP_FLAG_BITS, SH_FL_UNINIT);
	HEAP_LEAVE
}

/* memory extent was marked as symbolic */
void mmu_signal_memcheck(void* addr, uint64_t len)
{
	int	not_in = (in_heap == 0);

	if (not_in) HEAP_ENTER

	shadow_put_units_range(
		&heap_si,
		(long)addr,
		SH_FL_UNINIT,
		ROUND_BYTES2UNITS(&heap_si, len));

	if (not_in) HEAP_LEAVE
}

/* NOTE: it's important to NOT use cnulltlb on a
 * used shadow page. This is because we don't want to fast
 * path any potential accesses on a page which may have
 * a FREE value */
#define MMU_LOADC(x,y)				\
y mmu_load_##x##_memcheckc(void* addr)		\
{						\
if (!shadow_pg_used(&heap_si, (uint64_t)addr))	\
	return mmu_load_##x##_cnulltlb(addr);	\
if (!in_heap)					\
	check_r(addr, x/8, "Loading from", "const pointer"); \
return mmu_load_##x##_cnull(addr); }


#define MMU_STOREC(x,y)					\
void mmu_store_##x##_memcheckc(void* addr, y v)		\
{							\
if (!shadow_pg_used(&heap_si, (uint64_t)addr)) {	\
	mmu_store_##x##_cnulltlb(addr, v);		\
	return;						\
}							\
if (!in_heap)	\
	check_w(addr, x/8, "Storing to", "const pointer");	\
mmu_store_##x##_cnull(addr, v); }


#define MMU_LOAD(x,y)			\
y mmu_load_##x##_memcheck(void* addr)	\
{					\
if (!in_sym_mmu) {			\
in_sym_mmu++;				\
if (!in_heap)				\
	check_r(addr, x/8, "Loading from", "sym pointer");	\
in_sym_mmu--;	\
}	\
return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_memcheck(void* addr, y v)	\
{ \
if (!in_sym_mmu) {	\
in_sym_mmu++;	\
if (!in_heap)	\
	check_w(addr, x/8, "Storing to", "sym pointer");	\
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

struct mmu_ops mmu_ops_memcheck = {
	DECL_MMUOPS(memcheck) 
	.mo_signal = mmu_signal_memcheck,
	.mo_init = mmu_init_memcheck };
struct mmu_ops mmu_ops_memcheckc = { DECL_MMUOPS_ALL(memcheckc) };

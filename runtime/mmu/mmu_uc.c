#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"
#include "uc.h"

static struct uc_tab	uct = { .uct_ent_c = 0, .uct_last_uce = 0 };

#define CLEANUP_OPT
#ifdef CLEANUP_OPT
#define NO_CLEANUP do_cleanup = 0
#define HAS_CLEANUP (do_cleanup != 0)
static int do_cleanup = 1;
#else
#define NO_CLEANUP
#define HAS_CLEANUP	1
#endif

#define UCE_END_PG(y) 	((UCE_END(y)+4095) & (~0xfffUL))
#define UC_ASSUME_FOLLOWS(x, y)	klee_assume_ugt(UCE_BEGIN(x), UCE_END_PG(y))
#define UC_MAY_FOLLOW(x,y) klee_feasible_ugt(UCE_BEGIN(x), UCE_END_PG(y))
#define UCTAB_UCE(i)	uct.uct_ents[i].uch_uce
#define UCTAB_IS_ALIAS(i)	((int)uct.uct_ents[i].uch_uce->uce_n != (i+1))

static struct uce_backing* uc_make_backing(
	struct uc_ent* uce, unsigned int radius)
{
	struct uce_backing	*ret;
	unsigned		sz;
	unsigned		lo, hi, i, j;

	sz = sizeof(struct uce_backing) + (radius*2+1);
	ret = malloc(sz);

	klee_make_symbolic(ret, sz, "ucb");
	klee_assume_eq(uce->uce_n, ret->ucb_uce_n);
	uce->uce_n = ret->ucb_uce_n;

	if (uce->uce_b == NULL)
		return ret;

	lo = radius - uce->uce_radius_phys;
	hi = lo + (uce->uce_radius_phys*2);

	for (i = lo, j = 0; i < hi; i++, j++) {
		klee_assume_eq(ret->ucb_dat[i], uce->uce_b->ucb_dat[j]);
		ret->ucb_dat[i] = uce->uce_b->ucb_dat[j];
	}

	return ret;
}

/* induce ordering */
static int uc_suppress_aliasing(void* addr)
{
	struct uc_ent	*prev, tmp;

	prev = uct.uct_last_uce;
	if (prev == NULL)
		return 1;

	tmp.access.a_pivot = addr;
	tmp.uce_radius = MIN_RADIUS;

	if (!UC_MAY_FOLLOW(&tmp, prev))
		return 0;

	UC_ASSUME_FOLLOWS(&tmp, prev);
	return 1;
}

static struct uc_h_ent* uc_make(void* addr)
{
	struct uc_ent	*ret;
	int		i;

	if (uct.uct_ent_c == MAX_UCE) {
		klee_uerror("Ran out of UC Entries", "uc.max.err");
		return NULL;
	}

	i = uct.uct_ent_c++;
	if (i == MAX_UCE)
		return NULL;

	klee_assert (UCTAB_UCE(i) == NULL);

	if (!uc_suppress_aliasing(addr)) {
		/* find alias */
		int	j;
		klee_print_expr("DETECT ALIAS", addr);
		for (j = 0; j < i; j++) {
			if (	!UCTAB_IS_ALIAS(j) &&
				klee_feasible_ule(addr, UCE_END(UCTAB_UCE(j))))
				break;
		}

		if (j == i) {
			klee_uerror(
				"Could not make sequential alloc or find alias",
				"aliasing.err");
		}

		uct.uct_ents[i].uch_uce = uct.uct_ents[j].uch_uce;
		klee_print_expr("RET ALIAS", UCTAB_UCE(i)->access.a_pivot);
		return &uct.uct_ents[i];
	}

	ret = malloc(sizeof(struct uc_ent));
	UCTAB_UCE(i) = ret;
	uct.uct_last_uce = ret;

	klee_make_symbolic(ret, sizeof(*ret), "uce");

	klee_assume_eq(ret->access.a_pivot, addr);
	ret->access.a_pivot = addr;

	klee_assume_uge(ret->access.a_max, addr);
	klee_assume_ule(ret->access.a_min, addr);

	klee_assume_eq(ret->uce_n, uct.uct_ent_c);
	ret->uce_n = uct.uct_ent_c;

	/* first try-- no entry */
	if (ret->uce_radius == 0) {
		NO_CLEANUP;
		klee_uerror("Dereferenced bad UC pointer", "uc.empty.err");
		return NULL;
	}

	ret->uce_depth = 0;
	klee_assume_uge(ret->uce_radius, MIN_RADIUS);
	klee_assume_ule(ret->uce_radius, MAX_RADIUS);

	ret->uce_b = NULL;
	ret->uce_radius_phys = MIN_RADIUS;
	ret->uce_b = uc_make_backing(ret, MIN_RADIUS);

	return &uct.uct_ents[i];
}

static struct uc_h_ent* uc_make_in_arena(void* addr)
{
	uint64_t	addr_m;

	/* *one* query check to make sure concrete value
	 * is not masquerading as a symbolic. */
	addr_m = ((uint64_t)addr) & UC_ARENA_MASK;
	if (!klee_feasible_eq(addr_m, UC_ARENA_BEGIN))
		return NULL;


	/* keep inside of arena */
	klee_assume_eq(addr_m, UC_ARENA_BEGIN);

	return uc_make(addr);
}

static struct uc_ent* uc_get(void* addr)
{
	struct uc_h_ent	*uch;
	unsigned	i, uce_c;
	uint64_t	a_hash;

	a_hash = klee_sym_corehash(addr);
	if (a_hash == 0) {
		if (!klee_feasible_eq(
			((uint64_t)addr) & UC_ARENA_BEGIN, UC_ARENA_BEGIN))
		{
			/* wasn't allocable anyway */
			return NULL;
		}

		klee_print_expr("Bad hash on addr", addr);
		return NULL;
	}

	/* find UC entry */
	uce_c = 0;
	for (i = 0; i < MAX_UCE && uce_c < uct.uct_ent_c; i++) {
		if (UCTAB_UCE(i) == NULL)
			continue;

		uce_c++;
		if (a_hash == uct.uct_ents[i].uch_pivot_hash)
			return UCTAB_UCE(i);
	}

	if (i == MAX_UCE)
		klee_uerror("Exceeded UC entries", "uc.maxent.err");

	/* no match found, make one.. */
	uch = uc_make_in_arena(addr);
	if (uch == NULL) return NULL;

	uch->uch_pivot_hash = a_hash;
	return uch->uch_uce;
}


static void uc_extend(struct uc_ent* uce, uint64_t min_radius)
{
	uint64_t		min_radius_c;
	struct uce_backing	*new_backing;

	min_radius_c = klee_min_value(min_radius);
	if (min_radius_c > MAX_RADIUS)
		klee_uerror("Max radius exceeded", "uc.max.err");

	if (min_radius_c < uce->uce_radius_phys)
		klee_uerror("Min radius < phys radius", "uc.rad.err");

	klee_print_expr("extending with min radius", min_radius_c);

	/* XXX: this should probably fork */
	if (min_radius == min_radius_c) {
		klee_assume_eq(min_radius, min_radius_c);
	} else if (min_radius < MAX_RADIUS) {
		min_radius_c = klee_max_value(min_radius);
		klee_assume_ule(min_radius, min_radius_c);
	} else {
		klee_uerror("Max radius exceeded by forking", "uc.max.err");
	}

	min_radius_c = 8*((min_radius_c + 7)/8);
	klee_assume_uge(uce->uce_radius, min_radius_c);

	new_backing = uc_make_backing(uce, min_radius_c);
	free(uce->uce_b);

	uce->uce_radius_phys = min_radius_c;
	uce->uce_b = new_backing;
}

static uint64_t  uc_getptr(struct uc_ent* uce, void* addr, unsigned w)
{
	ptrdiff_t	base_off;

	base_off = ((uint64_t)addr) - ((uint64_t)uce->access.a_pivot);

	if (base_off + uce->uce_radius_phys < 0) {
		/* out of range; underflow */
		if (-base_off > uce->uce_radius) {
			NO_CLEANUP;
			klee_uerror("Underflow", "uc.uf.err");
		}
		uc_extend(uce, -base_off);
	} else if ((base_off+w) > uce->uce_radius_phys) {
		/* out of range; overflow */
		if ((base_off+w) > uce->uce_radius) {
			NO_CLEANUP;
			klee_uerror("Overflow", "uc.of.err");
		}
		klee_print_expr("OF-extend", base_off+w);
		uc_extend(uce, base_off+w);
	}

	return (uint64_t)(&uce->uce_b->ucb_dat[0]
		+ uce->uce_radius_phys
		+ base_off);
}

void mmu_cleanup_uc(void)
{
	unsigned	i;

	if (uct.uct_ent_c == 0 || !HAS_CLEANUP)
		return;

	klee_assume_uge(UCE_BEGIN(UCTAB_UCE(0)), UC_ARENA_BEGIN);

	/* klee_min_value caches last result,
	 * two loops might perform better */
	for (i = 0; i < uct.uct_ent_c; i++) {
		struct uc_ent	*uce = UCTAB_UCE(i);
		uint64_t	min_radius;

		min_radius = klee_min_value(uce->uce_radius);
		klee_assume_eq(uce->uce_radius, min_radius);
		uce->uce_radius = min_radius;
	}

	for (i = 0; i < uct.uct_ent_c; i++) {
		struct uc_ent	*uce = UCTAB_UCE(i);
		uint64_t	min_addr;

		min_addr = klee_min_value((uint64_t)uce->access.a_pivot);
		klee_assume_eq((uint64_t)uce->access.a_pivot, min_addr);
		uce->access.a_pivot = (void*)min_addr;
	}
}

#define MMU_LOAD(x,y,z)		\
y mmu_load_##x##_uc(void* addr)	\
{	y		*p;	\
	uint64_t	c_64;	\
	struct uc_ent	*uce;	\
	uce = uc_get(addr);	\
	if (uce == NULL) { return mmu_load_ ##x## _ ##z (addr); } \
	c_64 = uc_getptr(uce, addr, x/8);	\
	p = (y*)c_64;				\
	return klee_is_symbolic((uint64_t)p)	\
		? mmu_load_ ##x## _ ##z (p)	\
		: *p; }

#define MMU_STORE(x,y,z)			\
void mmu_store_##x##_uc(void* addr, y v)	\
{	y *p;			\
	uint64_t	c_64;	\
	struct uc_ent	*uce;	\
	uce = uc_get(addr);	\
	if (uce == NULL) {			\
		mmu_store_##x##_##z (addr, v); 	\
		return;				\
	}					\
	c_64 = uc_getptr(uce, addr, x/8);	\
	p = (y*)c_64;				\
	if (klee_is_symbolic((uint64_t)p))	\
		mmu_store_##x##_##z (p, v); 	\
	else					\
		*p = v;	}

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y,objwide)	\
	MMU_STORE(x,y,objwide)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)

struct mmu_ops mmu_ops_uc = {
	DECL_MMUOPS(uc)
	.mo_signal = NULL,
	.mo_cleanup = mmu_cleanup_uc,
	.mo_init = NULL
};
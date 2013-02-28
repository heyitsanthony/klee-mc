/* XXX: this doesn't do anything yet */
#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"
#include "uc.h"

static struct uc_tab	uct = { .uct_ent_c = 0 };

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

	klee_print_expr("binding symbolics",  lo);
	for (i = lo, j = 0; i < hi; i++, j++) {
		klee_assume_eq(ret->ucb_dat[i], uce->uce_b->ucb_dat[j]);
		ret->ucb_dat[i] = uce->uce_b->ucb_dat[j];
	}

	return ret;
}

static struct uc_ent* uc_make(void* addr)
{
	struct uc_ent	*ret;
	unsigned	i;

	if (uct.uct_ent_c == MAX_UCE) {
		klee_uerror("Ran out of UC Entries", "uc.max.err");
		return NULL;
	}

	ret = NULL;
	for (i = 0; i < MAX_UCE; i++) {
		if (uct.uct_ents[i] == NULL) {
			uct.uct_ents[i] = malloc(sizeof(struct uc_ent));
			ret = uct.uct_ents[i];
			break;
		}
	}

	uct.uct_ent_c++;

	klee_make_symbolic(ret, sizeof(*ret), "uce");

	/* this is necessary so that UCE's can be matched with the pivot
	 * pointer; as it is, it uses the highest bits in the address,
	 * which probably isn't ideal. */
	if (klee_feasible_eq(get_uce_flag(addr), uct.uct_ent_c)) {
		klee_assume_eq(get_uce_flag(addr), uct.uct_ent_c);
	}
	/* in the else case, there's a fixed address that must be
	 * satisfied. I'm not sure what to do in the case. */

	klee_assume_eq(ret->access.a_pivot, addr);
	ret->access.a_pivot = addr;

	klee_assume_uge(ret->access.a_max, addr);
	klee_assume_ule(ret->access.a_min, addr);

	/* first try-- no entry */
	if (ret->uce_radius == 0) {
		klee_uerror("Dereferenced bad UC pointer", "uc.empty.err");
		return ret;
	}

	ret->uce_depth = 0;
	klee_assume_uge(ret->uce_radius, MIN_RADIUS);
	klee_assume_eq(ret->uce_n, uct.uct_ent_c);
	ret->uce_n = uct.uct_ent_c;

	ret->uce_b = NULL;
	ret->uce_radius_phys = MIN_RADIUS;
	ret->uce_b = uc_make_backing(ret, MIN_RADIUS);

	return ret;
}

static struct uc_ent* uc_get(void* addr)
{
	struct uc_ent	*ret = NULL;
	unsigned	i, uce_c;
	uint64_t	a_hash;

	a_hash = klee_sym_corehash(addr);
	if (a_hash == 0) {
		klee_print_expr("Bad hash on addr", addr);
		return NULL;
	}

	/* find UC entry */
	uce_c = 0;
	for (i = 0; i < MAX_UCE && uce_c < uct.uct_ent_c; i++) {
		if (uct.uct_ents[i] == NULL)
			continue;

		uce_c++;
		if (a_hash == uct.uct_ents[i]->uce_pivot_hash) {
			ret = uct.uct_ents[i];
			break;
		}
	}

	if (i == MAX_UCE)
		klee_uerror("Exceeded UC entries", "uc.maxent.err");

	/* no match found, make one.. */
	if (ret == NULL) {
		ret = uc_make(addr);
		ret->uce_pivot_hash = a_hash; 
	}

	return ret;
}


static void uc_extend(struct uc_ent* uce, uint64_t min_radius)
{
	uint64_t		min_radius_c;
	struct uce_backing	*new_backing;

	min_radius_c = klee_min_value(min_radius);
	if (min_radius_c > MAX_RADIUS) {
		klee_uerror("Max radius exceeded", "uc.max.err");
	}

	if (min_radius_c < uce->uce_radius_phys) {
		klee_uerror("Min radius smaller than phys radius", "uc.rad.err");
	}

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
			klee_print_expr("UF", base_off);
			klee_uerror("Underflow", "uc.uf.err");
		}
		uc_extend(uce, -base_off);
	} else if ((base_off+w) > uce->uce_radius_phys) {
		/* out of range; overflow */
		if ((base_off+w) > uce->uce_radius) {
			klee_print_expr("OF-base", base_off);
			klee_print_expr("OF-end", base_off+w);
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
	klee_print_expr("hello cleanup uc", 0);
}

#define MMU_LOAD(x,y,z)		\
y mmu_load_##x##_uc(void* addr)	\
{	y		*p;	\
	uint64_t	c_64;	\
	struct uc_ent	*uce;	\
	uce = uc_get(addr);	\
	if (uce == NULL) return mmu_load_ ##x## _ ##z (addr);	\
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
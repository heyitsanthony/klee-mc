/* XXX: this doesn't do anything yet */
#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

extern void* malloc(unsigned long n);
extern void free(void*);

#define MAX_RADIUS	4096
#define MIN_RADIUS	8

struct uc_ent
{
	/* symbolic stuff */
	struct {
		void* a_min;
		void* a_max;
		void* a_pivot;
	} access;

	unsigned int	uce_radius;

	/* physical stuff */
	uint64_t	uce_pivot_hash;
	void		*uce_backing;
	unsigned int	uce_radius_phys;
	unsigned int	uce_depth;
};

#define MAX_UCE	512
struct uc_tab
{
	struct uc_ent	*uct_ents[MAX_UCE];
	unsigned	uct_ent_c;
};

static struct uc_tab	uct = { .uct_ent_c = 0 };


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

	klee_print_expr("make symbolic uce", ret);
	klee_make_symbolic(ret, sizeof(*ret), "uce");

	klee_assume_eq(ret->access.a_pivot, addr);
	ret->access.a_pivot = addr;

	klee_assume_uge(ret->access.a_max, addr);
	klee_assume_ule(ret->access.a_min, addr);

	/* first try-- no entry */
	if (ret->uce_radius == 0) {
		klee_uerror("Dereferenced bad UC pointer", "uc.empty.err");
		return ret;
	}

	uct.uct_ent_c++;
	ret->uce_depth = 0;
	ret->uce_backing = malloc(MIN_RADIUS*2);
	ret->uce_radius_phys = MIN_RADIUS;
	klee_assume_uge(ret->uce_radius, MIN_RADIUS);
	klee_make_symbolic(ret->uce_backing, MIN_RADIUS*2, "uce_backing");

	return ret;
}

static struct uc_ent* uc_get(void* addr)
{
	struct uc_ent	*ret = NULL;
	unsigned	i, uce_c;
	uint64_t	a_hash;

	a_hash = klee_sym_corehash(addr);

	/* XXX: how to distinguish UC? no solver queries! */
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
	if (ret == NULL) ret = uc_make(addr);

	return ret;
}

static void uc_extend(struct uc_ent* uce, uint64_t min_radius)
{
	uint64_t	min_radius_c;
	char		*new_backing;
	unsigned	i, j, lo, hi;

	min_radius_c = klee_min_value(min_radius);
	if (min_radius_c > MAX_RADIUS) {
		klee_uerror("Max radius exceeded", "uc.max.err");
	}

	klee_print_expr("exnteind with min radius", min_radius_c);
	klee_assume_eq(min_radius, min_radius_c);

	min_radius_c = 8*((min_radius_c + 7)/8);

	klee_print_expr("bounding radius", uce->uce_radius);
	klee_assume_uge(uce->uce_radius, min_radius_c);

	new_backing = malloc(min_radius_c*2);
	klee_make_symbolic(new_backing, min_radius_c*2, "uce_backing");

	lo = min_radius_c - uce->uce_radius_phys;
	hi = lo + (uce->uce_radius_phys*2);

	klee_print_expr("binding symbolics",  lo);
	for (i = lo, j = 0; i < hi; i++, j++) {
		klee_assume_eq(new_backing[i], ((char*)uce->uce_backing)[j]);
		new_backing[i] = ((char*)uce->uce_backing)[j];
	}

	free(uce->uce_backing);
	uce->uce_radius_phys = min_radius_c;
	uce->uce_backing = new_backing;

	klee_print_expr("done binding", min_radius_c);
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
			klee_print_expr("OF", base_off);
			klee_uerror("Overflow", "uc.of.err");
		}
		uc_extend(uce, base_off+w);
	}

	return (uint64_t)((char*)uce->uce_backing
		+ uce->uce_radius_phys
		+ base_off);
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
	return klee_is_symbolic(p)		\
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
	if (klee_is_symbolic(p))		\
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
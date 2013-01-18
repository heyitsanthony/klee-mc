/* XXX: this doesn't do anything yet */
#include "klee/klee.h"
#include "mmu_testptr.h"
#include "mmu.h"

struct uc_ent
{
	struct {
		void* a_min;
		void* a_max;
		void* a_pivot;
	} access;

	uint64_t	uce_pivot_hash;
	void		*uce_backing;

	unsigned int	uce_radius;
	unsigned int	uce_depth;

	unsigned int	uce_in_use;
};

#define MAX_UCE	512
struct uc_tab
{
	struct uc_ent	uct_ents[512];
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
		if (!uct.uct_ents[i].uce_in_use) {
			ret = &uct.uct_ents[i];
			break;
		}
	}


	klee_make_symbolic(ret, sizeof(*ret), "uce");

	klee_assume_eq(ret->access.a_pivot, addr);
	klee_assume_uge(ret->access.a_max, addr);
	klee_assume_ule(ret->access.a_min, addr);
	
	/* first try-- no entry */
	if (ret->uce_radius == 0) {
		klee_uerror("Dereferenced pointer", "uc.empty.err");
		return ret;
	}

	uct.uct_ent_c++;
	ret->uce_depth = 0;
	ret->uce_in_use = 1;
	ret->uce_backing = malloc(16);
	klee_make_symbolic(ret->uce_backing, 16, "uce_backing");

	if (ret->uce_radius <= 16) {
		klee_assume_eq(ret->uce_radius, 16);
		return ret;
	}

	return ret;
}

static struct uc_ent* uc_get(void* addr)
{
	struct	uc_ent	*ret = NULL;
	unsigned	i, uce_c;
	uint64_t	a_hash;

	a_hash = klee_sym_corehash(addr);

	/* XXX: how to distinguish UC? no solver queries! */
	uce_c = 0;
	for (i = 0; i < MAX_UCE && uce_c < uct.uct_ent_c; i++) {
		if (!uct.uct_ents[i].uce_in_use)
			continue;

		uce_c++;
		if (a_hash == uct.uct_ents[i].uce_pivot_hash) {
			ret = &uct.uct_ents[i];
			break;
		}
	}

	/* no match found, make one.. */
	if (ret == NULL) {
		ret = uc_make(addr);
		return ret;
	}

	klee_uerror("STUB: is_uc_ptr", "stub.err");
	return 0;
}

static struct uc_ent* uc_getptr(struct uc_ent* uce, void* addr)
{
	/* XXX: how to distinguish UC? no solver queries! */
	klee_uerror("STUB: is_uc_ptr", "stub.err");
	return 0;
}


#define MMU_LOAD(x,y,z)		\
y mmu_load_##x##_uc(void* addr)	\
{	y		*p;	\
	uint64_t	c_64;	\
	struct uc_ent	*uce;	\
	uce = uc_get(addr);	\
	if (uce == NULL)	\
		return mmu_load_ ##x## _ ##z (addr);	\
	c_64 = uc_getptr(uce, addr);		\
	p = (y*)c_64;				\
	return *p; }

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
	c_64 = uc_getptr(uce, addr);		\
	p = (y*)c_64;		\
	*p = v;	}

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y,objwide)	\
	MMU_STORE(x,y,objwide)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "shadow.h"

#if 0

#define SHADOW_RECLAIM_MEM	1
//#define SHADOW_DEBUG	1

#define SHADOW_ERROR(x)		fprintf(stderr, "shadow: %s\n", x)

#define PAGE_MAX_SIZE		4096
#define phys_to_page(s, x)	(x / (s)->phys_bytes_ppg)
#define phys_to_off(s, x)	((((x % (s)->phys_bytes_ppg)/(s)->gran)\
					*(s)->bits)/8)
#define phys_to_bit(s,x)	((((x % (s)->phys_bytes_ppg)/(s)->gran)\
					*(s)->bits)%8)

#define si_is_tiny(s)		((s)->bits < 8)
#define si_is_small(s)		((s)->bits >= 8 && \
					((s)->bits <= (sizeof(uintptr_t)*8)))
#define si_is_large(s)		((s)->bits > (sizeof(uintptr_t)*8))


static shadow_page* shadow_new_page(struct shadow_info* si, int idx);
static void shadow_free_page(struct shadow_info* si, int pg_idx);
static void shadow_put_tiny(	struct shadow_info* si, shadow_page* p,
				uint64_t phys, uintptr_t l);
static void shadow_put_small(	struct shadow_info* si, shadow_page* p,
				uint64_t phys, uintptr_t l);
static uintptr_t shadow_get_tiny(	struct shadow_info* si, shadow_page* p, 
					uint64_t phys);
static uintptr_t shadow_get_small(	struct shadow_info* si, shadow_page* p, 
					uint64_t phys);
static int shadow_is_uninit(struct shadow_info* si, uintptr_t l);

static int bits(int n)
{
	int	i;
	int	x = 0;
	for(i = 0; i < sizeof(n) * 8; i++)
		if(n & (1 << i))
			x++;
	return x;
}

int shadow_init(
	struct shadow_info* si,
	int granularity, int bits_per_unit, void* initial)
{
	if(bits(bits_per_unit) != 1){
		SHADOW_ERROR("number of bits must be power of 2");
		return 0;
	}	

	si->si_gran = granularity;
	si->si_bits = bits_per_unit;
	si->si_units_ppg = (PAGE_MAX_SIZE * 8) / (si->gran*si->bits);
	si->si_phys_bytes_ppg = si->gran * si->units_ppg;

	si->si_alloced_pages = 0;
	si->si_bytes_ppg = (si->units_ppg * si->bits + 7)/ 8 + 	
			sizeof(uintptr_t);

	memset(si->si_bucket, 0, sizeof(si->si_bucket));

	si->si_mask = (si->si_bits == (sizeof(uintptr_t)*8))
		? ~0UL
		: (1UL << si->si_bits) - 1;

	si->si_initial = initial;

	return 1;
}

void shadow_fini(struct shadow_info* si)
{
	int	i, j;

	for (i = 0; i < SHADOW_PG_BUCKETS; i++) {
		struct shadow_pg_bucket	*spb;

		spb = &si->si_bucket[i];
		if (spb->spb_pgs == NULL) continue;

		for (j = 0; j < spb.spb_pg_c; j++)
			free(spb->spb_pgs[i]);

		free(spb->spb_pgs);
	}
}

static shadow_page* shadow_new_page(
	struct shadow_info* si, uint64_t ptr);
{
	struct shadow_pg_bucket	*spb;
	struct shadow_page	*pg;
	int			i;

	pg = malloc(sizeof(struct shadow_page) + si->si_bytes_ppg);
	pg->sp_refs = 0;

	/* initialize page with initial data */
	for(i = 0; i < si->sp_bytes_ppg / sizeof(uintptr_t); i++){
		((uintptr_t*)(&pg->pg_data))[i] = *((uintptr_t*)si->si_initial);
	}

	spb = &si->si_bucket[PTR2BUCKET(ptr)];

	/* allocate? */
	if (spb->spb_pgs == NULL) {
		assert (0 == 1 && "ARGH");
		spb->spb_max_pg_c = 8;
	}

	/* extend? */
	if (spb->spb_pg_c == spb->spb_max_pg_c) {
		struct shadow_page	**spp;

		spb->spb_max_pg_c *= 2;
		spp = malloc(sizeof(*spp) * spb->spb_max_pg_c);
		memset(spp, 0, sizeof(*spp) * spb->spb_max_pg_c);
		memcpy(spp, spb->spb_pgs, sizeof(*spp) * spb->spb_pg_c);

		free(spb->spb_pgs);
		spb->spb_pgs = spp;
	}

	spb->spb_pgs[spb->spb_pg_c++] = pg;
	si->si_alloced_pages++;

	return pg;
}

void shadow_put_range(	struct shadow_info* si, uint64_t phys, int units,
			uintptr_t l)
{
	/** XXX make better */
	int	i;
	for(i = 0; i < units; i++){
		shadow_put(si, phys + si->gran*i, l);
	}
}

void shadow_or_range(struct shadow_info* si, uint64_t phys, int units, 
		uintptr_t l)
{
	int	i;
	for(i = 0; i < units; i++){
		uintptr_t	in;
		in = shadow_get(si, phys + si->gran*i);
		shadow_put(si, phys + si->gran*i, in | l);
	}
}

static void shadow_put_small(	struct shadow_info* si, shadow_page* p,
				uint64_t phys, uintptr_t l)
{
	int		off = phys_to_off(si, phys);
	uintptr_t	data;

#ifdef SHADOW_RECLAIM_MEM
	if(memcmp(si->initial, &p->data[off], si->bits / 8) == 0)
		p->refs++;
#endif
	data = *((uintptr_t*)&p->data[off]);
	data &= ~si->mask;
	data |= l & si->mask;
	*((uintptr_t*)&p->data[off]) = data;
#ifdef SHADOW_RECLAIM_MEM
	if(memcmp(si->initial, &l, si->bits / 8) == 0)
		p->refs--;
#endif
}

static void shadow_put_tiny(	struct shadow_info* si, 
				shadow_page* p,
				uint64_t phys, uintptr_t l)
{
	int	off = phys_to_off(si, phys);
	int	data;
	int	old;

	data = p->data[off];

#ifdef SHADOW_RECLAIM_MEM
	old = (data & ((si->mask) << phys_to_bit(si, phys))) >> 
		phys_to_bit(si, phys);
	if(old == (*((uintptr_t*)si->initial) & si->mask))
		p->refs++;
#endif

	data &= ~((si->mask) << phys_to_bit(si, phys));
	data |= (l & si->mask) << phys_to_bit(si, phys);
	p->data[off] = data;

#ifdef SHADOW_RECLAIM_MEM
	if((l & si->mask) == (*((uintptr_t*)si->initial) & si->mask))
		p->refs--;
#endif
}

static int shadow_is_uninit(struct shadow_info* si, uintptr_t l)
{
	if(si_is_tiny(si)){
		if((l & si->mask) == (*((uintptr_t*)si->initial) & si->mask))
			return 1;
	}else if(si_is_small(si)){
		if(memcmp(si->initial, &l, si->bits / 8) == 0)
			return 1;
	}

	return 0;
}

void shadow_put(struct shadow_info* si, uint64_t phys, uintptr_t l)
{
	int		pg_idx;
	shadow_page	*p;

	assert(phys < ram_size);

	pg_idx = phys_to_page(si, phys);
	p = si->pages[pg_idx];
	if(p == NULL){
#ifdef SHADOW_RECLAIM_MEM
		/* keep from thrashing */
		if(shadow_is_uninit(si, l))
			return;
#endif
#error fix index
		p = shadow_new_page(si, pg_idx);
	}
	
	if(si_is_tiny(si)){
		shadow_put_tiny(si, p, phys, l);
	}else if(si_is_small(si)){
		shadow_put_small(si, p, phys, l);
	}else
		SHADOW_ERROR("expected non-large type for put");

#ifdef SHADOW_DEBUG
	assert(shadow_get(si, phys) == (l & si->mask));
#endif

#ifdef SHADOW_RECLAIM_MEM
	if(p->refs == 0)
		shadow_free_page(si, pg_idx);
#endif
}

void shadow_put_large(struct shadow_info* si, uint64_t phys, const void* ptr)
{ assert(0 == 1); }

void shadow_get_large(struct shadow_info* si, uint64_t phys, void* ptr)
{ assert(0 == 1); }

static uintptr_t shadow_get_tiny(
	struct shadow_info* si, struct shadow_page* p, 
	uint64_t phys)
{
	int		off = phys_to_off(si, phys);
	uintptr_t	ret;

	ret = p->data[off];
	ret >>= phys_to_bit(si, phys);
	return ret & si->mask;
}

static uintptr_t shadow_get_uninit(struct shadow_info* si)
{
	uintptr_t	ret;
	ret = *((uintptr_t*)si->initial) & si->mask;
	return ret;
}

static uintptr_t shadow_get_small(
	struct shadow_info* si, struct shadow_page* p, uint64_t phys)
{
	int		off = phys_to_off(si, phys);
	uintptr_t	ret;

	ret = *(uintptr_t*)(&p->data[off]);

	return ret & si->mask;
}

uintptr_t shadow_get(struct shadow_info* si, uint64_t phys)
{
	uintptr_t	ret;
	shadow_page	*p;

	if(phys >= ram_size)
		fprintf(stderr, "oops! %x vs %x\n", phys, ram_size);
	assert(phys < ram_size);

	assert(!si_is_large(si));

	p = si->pages[phys_to_page(si, phys)];
	if(p == NULL){
		ret = shadow_get_uninit(si);
		return ret;
	}

	if(si_is_tiny(si))
		ret = shadow_get_tiny(si, p, phys);
	else if(si_is_small(si))
		ret = shadow_get_small(si, p, phys);
	else{	
		SHADOW_ERROR("Expected non-large shadow type");
		ret = ~0UL;
	}

	return ret;
}

static void shadow_free_page(struct shadow_info* si, int pg_idx)
{
	shadow_page	*p;

	p = si->pages[pg_idx];
	assert(p->refs == 0);

	qemu_free(p);
	si->alloced_pages--;
	si->pages[pg_idx] = NULL;
	return;
}
#endif

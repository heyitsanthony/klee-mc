#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "klee/klee.h"
#include "shadow.h"

#define SHADOW_RECLAIM_MEM	1
//#define SHADOW_DEBUG	1

#define PAGE_MAX_SIZE		4096
#define PTR2BUCKET(x)		((((uint64_t)(x))/PAGE_MAX_SIZE) % SHADOW_PG_BUCKETS)
#define PTR2PGNUM(x)		((((uint64_t)(x))/PAGE_MAX_SIZE))
#define PGNUM2PTR(x)		(void*)((uint64_t)(x) * PAGE_MAX_SIZE)

#define phys_to_off(s, x)	(((((x) % (s)->si_phys_bytes_ppg)/(s)->si_gran)\
					*(s)->si_bits)/8)
#define phys_to_bit(s,x)	(((((x) % (s)->si_phys_bytes_ppg)/(s)->si_gran)\
					*(s)->si_bits)%8)

#define si_is_tiny(s)		((s)->si_bits < 8)
#define si_is_small(s)		((s)->si_bits >= 8 && \
					((s)->si_bits <= (sizeof(uint64_t)*8)))
#define si_is_large(s)		((s)->si_bits > (sizeof(uint64_t)*8))


static struct shadow_page* shadow_new_page(struct shadow_info* si, uint64_t ptr);
static void shadow_free_page(struct shadow_info* si, struct shadow_page* pg);
static struct shadow_page* shadow_pg_get(struct shadow_info* si, uint64_t ptr);
static void shadow_put_tiny(
	struct shadow_info* si,
	struct shadow_page* p,
	uint64_t phys, uint64_t l);
static void shadow_put_small(
	struct shadow_info* si,
	struct shadow_page* p,
	uint64_t phys, uint64_t l);
static uint64_t shadow_get_tiny(
	struct shadow_info* si, struct shadow_page* p, uint64_t phys);
static uint64_t shadow_get_small(
	struct shadow_info* si, struct shadow_page* p, uint64_t phys);
static int shadow_is_uninit(struct shadow_info* si, uint64_t l);

static int bits(int n)
{
	unsigned	i, x =0;

	for(i = 0; i < sizeof(n) * 8; i++)
		if (n & (1 << i))
			x++;
	return x;
}

int shadow_init(
	struct shadow_info* si,
	int granularity, int bits_per_unit, uint64_t initial)
{
	if (bits(bits_per_unit) != 1){
		return 0;
	}

	si->si_gran = granularity;
	si->si_bits = bits_per_unit;
	si->si_units_ppg = (PAGE_MAX_SIZE * 8) / (si->si_gran*si->si_bits);
	si->si_phys_bytes_ppg = si->si_gran * si->si_units_ppg;

	si->si_alloced_pages = 0;
	si->si_bytes_ppg = (si->si_units_ppg * si->si_bits + 7)/ 8 + 
			sizeof(uint64_t);

	memset(si->si_bucket, 0, sizeof(si->si_bucket));

	si->si_mask = (si->si_bits == (sizeof(uint64_t)*8))
		? ~0UL
		: (1UL << si->si_bits) - 1;

	si->si_initial = initial;

	return 1;
}

void shadow_fini(struct shadow_info* si)
{
	unsigned i, j;

	for (i = 0; i < SHADOW_PG_BUCKETS; i++) {
		struct shadow_pg_bucket	*spb;

		spb = &si->si_bucket[i];
		if (spb->spb_pgs == NULL) continue;

		for (j = 0; j < spb->spb_pg_c; j++)
			free(spb->spb_pgs[i]);

		free(spb->spb_pgs);
	}
}

static struct shadow_page* shadow_new_page(struct shadow_info* si, uint64_t ptr)
{
	struct shadow_pg_bucket	*spb;
	struct shadow_page	*pg;
	uint64_t		*pg_dat;
	unsigned		i;

	pg = malloc(sizeof(struct shadow_page) + si->si_bytes_ppg);
	pg->sp_refs = 0;
	pg->sp_pgnum = PTR2PGNUM(ptr);
	pg_dat = (uint64_t*)(pg->sp_data);

	/* initialize page with initial data */
	for (i = 0; i < si->si_bytes_ppg / sizeof(uint64_t); i++)
		pg_dat[i] = si->si_initial;

	spb = &si->si_bucket[PTR2BUCKET(ptr)];

	/* allocate new bucket? */
	if (spb->spb_pgs == NULL) {
		spb->spb_max_pg_c = 8;
		spb->spb_pgs = calloc(spb->spb_max_pg_c, sizeof(pg));
	}

	/* extend bucket? */
	if (spb->spb_pg_c == spb->spb_max_pg_c) {
		struct shadow_page	**spp;

		spb->spb_max_pg_c *= 2;
		spp = calloc(spb->spb_max_pg_c, sizeof(*spp));
		memcpy(spp, spb->spb_pgs, sizeof(*spp) * spb->spb_pg_c);

		free(spb->spb_pgs);
		spb->spb_pgs = spp;
	}

	spb->spb_pgs[spb->spb_pg_c++] = pg;
	si->si_alloced_pages++;

	return pg;
}

void shadow_put_range(
	struct shadow_info* si, uint64_t phys, uint64_t l,
	unsigned units)
{
	unsigned i;
	for(i = 0; i < units; i++) shadow_put(si, phys + si->si_gran*i, l);
}

void shadow_or_range(
	struct shadow_info* si, uint64_t phys, int units, uint64_t l)
{
	int	i;
	for(i = 0; i < units; i++){
		uint64_t	in;
		in = shadow_get(si, phys + si->si_gran*i);
		shadow_put(si, phys + si->si_gran*i, in | l);
	}
}

static void shadow_put_small(
	struct shadow_info* si, struct shadow_page* sp,
	uint64_t phys, uint64_t l)
{
	int		off = phys_to_off(si, phys);
	uint64_t	data;

#ifdef SHADOW_RECLAIM_MEM
	if (memcmp(&si->si_initial, &sp->sp_data[off], si->si_bits / 8) == 0)
		sp->sp_refs++;
#endif
	data = *((uint64_t*)&sp->sp_data[off]);
	data &= ~si->si_mask;
	data |= l & si->si_mask;
	*((uint64_t*)&sp->sp_data[off]) = data;
#ifdef SHADOW_RECLAIM_MEM
	if (memcmp(&si->si_initial, &l, si->si_bits / 8) == 0)
		sp->sp_refs--;
#endif
}

static void shadow_put_tiny(
	struct shadow_info* si,
	struct shadow_page* p, uint64_t phys, uint64_t l)
{
	unsigned	off = phys_to_off(si, phys);
	uint64_t	data, old;

	data = p->sp_data[off];

#ifdef SHADOW_RECLAIM_MEM
	old = (data & ((si->si_mask) << phys_to_bit(si, phys))) >>
		phys_to_bit(si, phys);
	if (old == (si->si_initial & si->si_mask))
		p->sp_refs++;
#endif

	data &= ~((si->si_mask) << phys_to_bit(si, phys));
	data |= (l & si->si_mask) << phys_to_bit(si, phys);
	p->sp_data[off] = data;

#ifdef SHADOW_RECLAIM_MEM
	if ((l & si->si_mask) == (si->si_initial & si->si_mask))
		p->sp_refs--;
#endif
}

static int shadow_is_uninit(struct shadow_info* si, uint64_t l)
{
	if (si_is_tiny(si)){
		if ((l & si->si_mask) == (si->si_initial & si->si_mask))
			return 1;
	}else if (si_is_small(si)){
		if (memcmp(&si->si_initial, &l, si->si_bits / 8) == 0)
			return 1;
	}

	return 0;
}

static struct shadow_page* shadow_pg_get(struct shadow_info* si, uint64_t ptr)
{
	struct shadow_pg_bucket	*spb;
	unsigned		i;
	uint64_t		pgnum;

	pgnum = PTR2PGNUM(ptr);
	spb = &si->si_bucket[PTR2BUCKET(ptr)];
	for (i = 0; i < spb->spb_pg_c; i++)
		if (spb->spb_pgs[i]->sp_pgnum == pgnum)
			return spb->spb_pgs[i];

	return NULL;
}

void shadow_put(struct shadow_info* si, uint64_t phys, uint64_t l)
{
	struct shadow_page	*p;

	p = shadow_pg_get(si, phys);
	if (p == NULL){
#ifdef SHADOW_RECLAIM_MEM
		/* keep from thrashing */
		if (shadow_is_uninit(si, l))
			return;
#endif
		p = shadow_new_page(si, phys);
	}

	if (si_is_tiny(si)){
		shadow_put_tiny(si, p, phys, l);
	}else if (si_is_small(si)){
		shadow_put_small(si, p, phys, l);
	}else {
		klee_assert(0 == 1 && "expected non-large type for put");
	}

#ifdef SHADOW_DEBUG
	klee_assert(shadow_get(si, phys) == (l & si->si_mask));
#endif

#ifdef SHADOW_RECLAIM_MEM
	if (p->sp_refs == 0)
		shadow_free_page(si, p);
#endif
}

int shadow_pg_used(struct shadow_info* si, uint64_t phys)
{ return shadow_pg_get(si, phys) != NULL; }

void shadow_put_large(struct shadow_info* si, uint64_t phys, const void* ptr)
{ klee_assert(0 == 1); }

void shadow_get_large(struct shadow_info* si, uint64_t phys, void* ptr)
{ klee_assert(0 == 1); }

static uint64_t shadow_get_tiny(
	struct shadow_info* si, struct shadow_page* p,
	uint64_t phys)
{
	int		off = phys_to_off(si, phys);
	uint64_t	ret;

	ret = p->sp_data[off];
	ret >>= phys_to_bit(si, phys);
	return ret & si->si_mask;
}

static uint64_t shadow_get_uninit(struct shadow_info* si)
{ return si->si_initial & si->si_mask; }

static uint64_t shadow_get_small(
	struct shadow_info* si, struct shadow_page* p, uint64_t phys)
{
	int		off = phys_to_off(si, phys);
	uint64_t	ret;

	ret = *(uint64_t*)(&p->sp_data[off]);

	return ret & si->si_mask;
}

uint64_t shadow_get(struct shadow_info* si, uint64_t phys)
{
	struct shadow_page	*p;

	klee_assert(!si_is_large(si));

	p = shadow_pg_get(si, phys);
	if (p == NULL)
		return shadow_get_uninit(si);

	if (si_is_tiny(si))
		return shadow_get_tiny(si, p, phys);
	else if (si_is_small(si))
		return shadow_get_small(si, p, phys);

	return ~0UL;
}

static void shadow_free_page(struct shadow_info* si, struct shadow_page *p)
{
	struct shadow_pg_bucket	*spb;
	unsigned	i, j;

	spb = &(si->si_bucket[PTR2BUCKET(p->sp_pgnum * 4096)]);
	for (i = 0; i < spb->spb_pg_c; i++) {
		if (spb->spb_pgs[i] == p)
			break;
	}

	/* couldn't find page?? */
	if (i == spb->spb_pg_c)
		return;

	spb->spb_pgs[i] = NULL;
	for (j = i+1; j < spb->spb_pg_c; j++)
		spb->spb_pgs[j-1] = spb->spb_pgs[j];

	spb->spb_pg_c--;
	si->si_alloced_pages--;

	free(p);
	return;
}

void* shadow_next_pg(struct shadow_info* si, void* prev)
{
	unsigned	i;
	unsigned	pgnum, bidx;

	pgnum = PTR2PGNUM(prev);
	bidx = PTR2BUCKET(prev);

	/* search all buckets */
	for (i = bidx; i < SHADOW_PG_BUCKETS; i++) {
		struct shadow_pg_bucket	*spb;
		unsigned		j;

		spb = &si->si_bucket[i];
		if (spb == NULL)
			continue;

		/* search bucket */
		for (j = 0; j < spb->spb_pg_c; j++) {
			if (pgnum < spb->spb_pgs[j]->sp_pgnum)
				return PGNUM2PTR(spb->spb_pgs[j]->sp_pgnum);
		}
	}

	return NULL;
}

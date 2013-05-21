/** efficient shadow memory support */
#ifndef SHADOW_H
#define SHADOW_H

#include <stdint.h>

#define SHADOW_PG_BUCKETS	256


/* might want to add statistic tracking on last update */
struct shadow_page {
	uint64_t	sp_pgnum;	/* base address */
	uintptr_t	sp_refs;	/* forces alignment */
	uint8_t		*sp_data;	/* PAGE_MAX_SIZE follows */
};

struct shadow_pg_bucket
{
	unsigned		spb_pg_c;
	unsigned		spb_max_pg_c;
	struct shadow_page	**spb_pgs;
};

struct shadow_info {
	unsigned	si_gran;	/* number of phys bytes a unit rep */
	unsigned	si_bits;	/* number of bits shadowing a unit */
	unsigned	si_units_ppg;
	unsigned	si_phys_bytes_ppg;
	unsigned	si_bytes_ppg;
	unsigned	si_alloced_pages;

	/* default data for a shadow unit */
	/* must be at least uintptr_t in size */
	uint64_t	si_initial;
	uintptr_t	si_mask;

	struct shadow_pg_bucket	si_bucket[SHADOW_PG_BUCKETS];
};

int shadow_init(
	struct shadow_info*, int granularity, int bits_per_unit, uint64_t initial);
void shadow_fini(struct shadow_info* si);
void shadow_put(struct shadow_info* si, uint64_t phys, uint64_t l);
void shadow_put_large(struct shadow_info* si, uint64_t phys, const void* ptr);
void shadow_put_units_range(
	struct shadow_info* si, uint64_t phys, uint64_t l, unsigned units);

void* shadow_next_pg(struct shadow_info* si, void* last_addr);

void shadow_or_range(
	struct shadow_info* si, uint64_t phys, int units, 
	uint64_t l);
uint64_t shadow_get(struct shadow_info* si, uint64_t phys);
int shadow_pg_used(struct shadow_info* si, uint64_t phys);
void shadow_get_large(struct shadow_info* si, uint64_t phys, void* ptr);

int shadow_used_range(
	struct shadow_info* si,
	uint64_t phys,
	unsigned* idx_first,
	unsigned* idx_last);

#define shadow_used_bytes(x)	((x)->alloced_pages * (x)->bytes_per_page)

#endif

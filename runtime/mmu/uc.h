#ifndef MMU_UC_H
#define MMU_UC_H

#include <stdint.h>

#define MAX_RADIUS	4096
#define MIN_RADIUS	8


#define MAX_UCE		512
#define MAX_UCE_BITS	9


#define UC_ARENA_BEGIN_32	        0x10000000
#define UC_ARENA_END_32		        0x20000000
#define UC_ARENA_MASK_32	0xfffffffff0000000

#define UC_ARENA_BEGIN_64		0x100000000
#define UC_ARENA_END_64			0x200000000
#define UC_ARENA_MASK_64	 0xffffffff00000000

#define UC_ARENA_BEGIN		UC_ARENA_BEGIN_32
#define UC_ARENA_END		UC_ARENA_END_32
#define UC_ARENA_MASK		UC_ARENA_MASK_32

#define UCE_END(x)	((uint64_t)((x)->access.a_pivot) + ((x)->uce_radius))
#define UCE_BEGIN(x)	((uint64_t)((x)->access.a_pivot) - ((x)->uce_radius))

struct uc_ent
{
	/* symbolic stuff */
	struct {
		void* a_min;
		void* a_max;
		void* a_pivot;
	} access;

	/* index */
	unsigned int	uce_n;
	unsigned int	uce_radius;

	/* physical stuff */
	struct uce_backing	*uce_b;
	unsigned int		uce_radius_phys;
	unsigned int		uce_depth;
};

struct uc_h_ent
{
	uint64_t	uch_pivot_hash;
	struct uc_ent*	uch_uce;
};

struct uce_backing
{
	unsigned int	ucb_uce_n;
	char		ucb_dat[];
};

struct uc_tab
{
	struct uc_h_ent	uct_ents[MAX_UCE];
	unsigned	uct_ent_c;
	/* last uce added-- necessary to avoid walking
	 * aliases from tail of entry table */
	struct uc_ent	*uct_last_uce;
};


#endif

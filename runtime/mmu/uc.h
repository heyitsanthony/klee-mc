#ifndef MMU_UC_H
#define MMU_UC_H

#define MAX_RADIUS	4096
#define MIN_RADIUS	8


#define MAX_UCE		512
#define MAX_UCE_BITS	9

#define UCE_ADDR_SHIFT		(63 - MAX_UCE_BITS)
#define UCE_ADDR_MASK		(((1ULL << MAX_UCE_BITS) - 1) << UCE_ADDR_SHIFT)
#define set_uce_flag(x, y)	((((uint64_t)x) & ~UCE_ADDR_MASK) | (y << UCE_ADDR_SHIFT))
#define get_uce_flag(x)		((((uint64_t)x) & UCE_ADDR_MASK) >> UCE_ADDR_SHIFT)

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
	uint64_t		uce_pivot_hash;
	struct uce_backing	*uce_b;
	unsigned int		uce_radius_phys;
	unsigned int		uce_depth;
};

struct uce_backing
{
	unsigned int	ucb_uce_n;
	char		ucb_dat[];
};

struct uc_tab
{
	struct uc_ent	*uct_ents[MAX_UCE];
	unsigned	uct_ent_c;
};


#endif

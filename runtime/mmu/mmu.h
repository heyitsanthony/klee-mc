#ifndef RT_SYMMMU_H
#define RT_SYMMMU_H

#include <stdint.h>

/* ex. mmu_load_8_objwide */
#define DEF_LOAD(x, y)		y mmu_load_##x(void* addr);
#define DEF_STORE(x, y)		void mmu_store_##x(void* addr, y v);
#define DEF_ACCESS(a,x,y)	\
	DEF_LOAD(x ## _ ## a,y);	\
	DEF_STORE(x ## _ ## a,y);

#define DEF_MMU(a)			\
	DEF_ACCESS(a, 8, uint8_t)	\
	DEF_ACCESS(a, 16, uint16_t)	\
	DEF_ACCESS(a, 32, uint32_t)	\
	DEF_ACCESS(a, 64, uint64_t)	\
	DEF_ACCESS(a, 128, __uint128_t)
DEF_MMU(uniqptr)
DEF_MMU(null)
DEF_MMU(fork)
DEF_MMU(forkall)
DEF_MMU(forkobj)
DEF_MMU(objwide)
DEF_MMU(uc)

#endif

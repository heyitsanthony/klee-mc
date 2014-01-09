#include "klee/klee.h"
#include "mmu.h"

#define MMU_STORE(x,y,z)		\
void mmu_store_##x##_inst(void* addr, y v) {	\
	if (!klee_is_symbolic((uint64_t)addr))	\
		*((y*)addr) = v;	\
	else	\
		mmu_store_##x##_##z(addr, v); }

#define MMU_LOAD(x,y,z)			\
y mmu_load_##x##_inst(void* addr) {	\
	return (!klee_is_symbolic((uint64_t)addr)) \
		? *((y*)addr)		\
		: mmu_load_##x##_##z(addr); }

#undef MMU_ACCESS
#define MMU_ACCESS(x,y,z)	\
	MMU_LOAD(x,y,z)		\
	MMU_STORE(x,y,z)

MMU_ACCESS(8, uint8_t, objwide)
MMU_ACCESS(16, uint16_t, objwide)
MMU_ACCESS(32, uint32_t, objwide)
MMU_ACCESS(64, uint64_t, objwide)
MMU_ACCESS(128, __uint128_t, objwide)

DECL_MMUOPS_S(inst);
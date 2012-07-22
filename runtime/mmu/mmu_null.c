#include "klee/klee.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y null_mmu_load_##x(void* addr) { return 0; }

#define MMU_STORE(x,y)			\
void null_mmu_store_##x(void* addr, y v) {}	\

#define MMU_ACCESS(x,y)	\
	MMU_LOAD(x,y)	\
	MMU_STORE(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, long long)

#include "klee/klee.h"
#include "mmu.h"

#define MMU_LOAD(x,y)		\
y mmu_load_##x##_null(void* addr) { return 0; }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_null(void* addr, y v) {}	\

MMU_ACCESS_ALL();
DECL_MMUOPS_S(null);

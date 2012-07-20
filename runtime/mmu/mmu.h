#ifndef RT_SYMMMU_H
#define RT_SYMMMU_H

#include <stdint.h>

#define DEF_LOAD(x, y)	y mmu_load_##x(void* addr)
#define DEF_STORE(x, y)	void mmu_store_##x(void* addr, y v)
#define DEF_ACCESS(x,y)	DEF_LOAD(x,y); DEF_STORE(x,y);

DEF_ACCESS(8, uint8_t)
DEF_ACCESS(16, uint16_t)
DEF_ACCESS(32, uint32_t)
DEF_ACCESS(64, uint64_t)
DEF_ACCESS(128, long long)

#endif

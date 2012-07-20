#include "klee/klee.h"
#include "mmu.h"

uint8_t mmu_load_8(void* addr) { return 0; }
uint16_t mmu_load_16(void* addr) { return 0; }
uint32_t mmu_load_32(void* addr) { return 0; }
uint64_t mmu_load_64(void* addr) { return 0; }
long long mmu_load_128(void* addr) { return 0; }

void mmu_store_8(void* addr, uint8_t v) { }
void mmu_store_16(void* addr, uint16_t v) { }
void mmu_store_32(void* addr, uint32_t v) { }
void mmu_store_64(void* addr, uint64_t v) { }
void mmu_store_128(void* addr, long long v) { }
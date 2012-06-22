#ifndef SC_MMAP_H
#define SC_MMAP_H

void* sc_mmap(void* regfile, uint64_t len);
void sc_munmap(void* regfile);
void* sc_brk(void* regfile);
void* sc_mremap(void* regfile);
#endif

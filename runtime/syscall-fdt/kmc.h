#pragma once
#include <klee/klee.h>

extern "C" {
	void* kmc_sc_regs(void*);
	void kmc_sc_bad(unsigned int);
	void kmc_free_run(uint64_t addr, uint64_t num_bytes);
	void kmc_exit(uint64_t);
	void klee_stack_trace();
	void kmc_make_range_symbolic(uint64_t, uint64_t, const char*);
	void* kmc_alloc_aligned(uint64_t, const char* name);
	void kmc_breadcrumb(struct breadcrumb* bc, unsigned int sz);
	long sc_concrete_file_snapshot(const char* path, size_t path_len);
	long sc_concrete_file_size(const char* path, size_t path_len);
	void sc_get_cwd(char* buf);
}

inline uint64_t concretize_u64(uint64_t s)
{
  uint64_t sc = klee_get_value(s);
  klee_assume(sc == s);
  return sc;
}
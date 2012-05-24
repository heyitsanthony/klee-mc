#include <sys/mman.h>
#include <klee/klee.h>
#include "syscalls.h"
#include "mem.h"

/* klee-mc fiddles with these on init */
uint64_t	heap_begin;
uint64_t	 heap_end;
static void	*last_brk = 0;


/* IMPORTANT: this will just *allocate* some data,
 * if you want symbolic, do it after calling this */
static void* sc_mmap_addr(void* regfile, void* addr, uint64_t len)
{
	int	is_himem;

	is_himem = (((intptr_t)addr & ~0x7fffffffffffULL) != 0);
	if (!is_himem) {
		/* not highmem, use if we've got it.. */
		addr = (void*)concretize_u64(GET_ARG0(regfile));
		klee_print_expr("non-highmem define fixed addr", addr);
		klee_define_fixed_object(addr, len);
		return addr;
	}

	/* can never satisfy a hi-mem request */
	if (GET_ARG2(regfile) & MAP_FIXED) {
		/* can never fixed map hi-mem */
		return MAP_FAILED;
	}

	/* toss back whatever */
	addr = kmc_alloc_aligned(len, "mmap");
	if (addr == NULL) addr = MAP_FAILED;
	return addr;
}

static void* sc_mmap_anon(void* regfile, uint64_t len)
{
	void		*addr;

	if (len <= 4096)
		len = concretize_u64(len);
	else if (len <= 16*1024)
		len = concretize_u64(len);
	else
		len = concretize_u64(len);

	/* mapping may be placed anywhere */
	if (GET_ARG0(regfile) == 0) {
		addr = kmc_alloc_aligned(len, "mmap");
		if (addr == NULL) addr = MAP_FAILED;
		return addr;
	}

	/* mapping has a deisred location */
	addr = sc_mmap_addr(regfile, (void*)GET_ARG0(regfile), len);
	return addr;
}

// return address of mmap
static void* sc_mmap_fd(void* regfile, uint64_t len)
{
	void		*ret_addr;

	/* TODO, how should this be split? */
	if (len <= 4096) {
		len = concretize_u64(len);
	} else if (len <= 8192) {
		len = concretize_u64(len);
	} else if (len <= (16*1024)) {
		len = concretize_u64(len);
	} else {
		len = concretize_u64(len);
	}

	ret_addr = sc_mmap_anon(regfile, len);
	if (ret_addr == MAP_FAILED) return ret_addr;

	make_sym((uint64_t)ret_addr, len, "mmap_fd");
	return ret_addr;
}

void* sc_mmap(void* regfile, uint64_t len)
{
	void		*addr;
	void		*new_regs;

	new_regs = sc_new_regs(regfile);

	if (len >= (uintptr_t)0x10000000 || (int64_t)len <= 0) {
		addr = MAP_FAILED;
	} else if (((int)GET_ARG4(regfile)) != -1) {
		/* file descriptor mmap-- things need to be symbolic */
		addr = sc_mmap_fd(regfile, len);
	} else if ((GET_ARG3(regfile) & MAP_ANONYMOUS) == 0) {
		/* !fd && !anon => WTF */
		addr = MAP_FAILED;
	} else {
		addr = sc_mmap_anon(regfile, len);
	}

	sc_ret_v_new(new_regs, (uint64_t)addr);
	return new_regs;
}

void sc_munmap(void* regfile)
{
	uint64_t	addr, num_bytes;

	addr = klee_get_value(GET_ARG0(regfile));
	num_bytes = klee_get_value(GET_ARG1(regfile));
	kmc_free_run(addr, num_bytes);

	/* always succeeds, don't bother with new regctx */
	sc_ret_v(regfile, 0);
}

void* sc_brk(void* regfile)
{
	// ptrdiff_t grow_len;

	klee_warning("Don't grow brks! This breaks static linking!");
	if (last_brk == 0)
		last_brk = (void*)heap_end;
	sc_ret_v(regfile, (uintptr_t)last_brk);
	return regfile;
}

#if 0
	new_regs = sc_new_regs(regfile);

	/* error case -- linux returns current break */
	if (GET_SYSRET(new_regs) == (uintptr_t)last_brk) {
		sc_ret_v_new(new_regs, last_brk);
		break;
	}

	/* don't forget:
	 * heap grows forward into a stack that grows down! */
	grow_len = GET_ARG0(regfile) - (intptr_t)last_brk;

	if (grow_len == 0) {
		/* request nothing */
		klee_warning("Program requesting empty break? Weird.");
	} else if (grow_len < 0) {
		/* deallocate */
		uint64_t	dealloc_base;

		dealloc_base = (intptr_t)last_brk + grow_len;
		num_bytes = -grow_len;
		kmc_free_run(dealloc_base, num_bytes);

		last_brk = (void*)dealloc_base;
	} else {
		/* grow */
		last_brk
	}

	sc_ret_v_new(new_regs, last_brk);
	break;
#endif

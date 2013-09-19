#include <stdbool.h>
#include <sys/mman.h>
#include <klee/klee.h>
#include "syscalls.h"
#include "concrete_fd.h"
#include "mem.h"

/* klee-mc fiddles with these on init */
uint64_t	heap_begin;
uint64_t	 heap_end;
static void	*last_brk = 0;


/* IMPORTANT: this will just *allocate* some data,
 * if you want symbolic, do it after calling this */
static void* sc_mmap_addr(void* addr, uint64_t len, int flags)
{
	int	is_himem;

	is_himem = (((intptr_t)addr & ~0x7fffffffffffULL) != 0);
	if (!is_himem) {
		/* not highmem, use if we've got it.. */
		unsigned	i;
		addr = concretize_ptr(addr);
		// klee_print_expr("non-highmem define fixed addr", addr);
		for (i = 0; i < (len+4095)/4096; i++)
			klee_define_fixed_object((char*)addr+(i*4096), 4096);
		return addr;
	}

	/* can never satisfy a hi-mem request */
	if (flags & MAP_FIXED) {
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

	/* mapping has a desired location */
	addr = sc_mmap_addr(GET_ARG0_PTR(regfile), len, GET_ARG3(regfile));
	return addr;
}

// return address of mmap
static void* sc_mmap_fd(void* regfile, uint64_t len, int fd, uint64_t off)
{
	void		*ret_addr;

	if (fd_is_concrete(fd)) {
		ssize_t	br;
		ret_addr = sc_mmap_anon(regfile, len);
		if (ret_addr == MAP_FAILED) return ret_addr;
		br = fd_pread(fd, ret_addr, len, off);
		fd_mark(fd, ret_addr, len, off);
		return ret_addr;
	}

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
	int		fd;

	new_regs = sc_new_regs(regfile);
	fd = ((int)GET_ARG4(regfile));

	if (len >= (uintptr_t)0x10000000 || (int64_t)len <= 0) {
		addr = MAP_FAILED;
	} else if (fd != -1) {
		/* file descriptor mmap-- things need to be symbolic */
		addr = sc_mmap_fd(regfile, len, fd, GET_ARG5(regfile));
	} else if ((GET_ARG3(regfile) & MAP_ANONYMOUS) == 0) {
		/* !fd && !anon => WTF */
		addr = MAP_FAILED;
	} else {
		addr = sc_mmap_anon(regfile, len);
	}

	sc_ret_v_new(new_regs, (uint64_t)addr);
	return new_regs;
}

void* sc_mremap(void* regfile)
{
	void	*old_addr = GET_ARG0_PTR(regfile);
	size_t	old_sz = GET_ARG1(regfile);
	size_t	new_sz = GET_ARG2(regfile);
	int	flags = GET_ARG3(regfile);
	void	*new_base;
	bool	is_range_free;
	unsigned i;

	if (flags & MREMAP_FIXED) {
		/* XXX: honor fixed reqs */
		sc_ret_v(regfile, ~0);
		return regfile;
	}

	old_addr = concretize_ptr(old_addr);
	new_base = ((char*)old_addr) + old_sz;

	/* this is just a munmap */
	if (new_sz < old_sz) {
		unsigned	rmv_sz = old_sz - new_sz;
		rmv_sz = concretize_u64(rmv_sz);
		kmc_free_run((uint64_t)old_addr + new_sz, rmv_sz);
		sc_ret_v(regfile, (uint64_t)old_addr);
		return regfile;
	}


	is_range_free = true;
	for (i = 0; i < ((new_sz - old_sz)+4095)/4096; i++) {
		if (klee_is_valid_addr(((char*)new_base)+i*4096)) {
			is_range_free = false;
			break;
		}
	}

	/* room is available to extend segment */
	if (is_range_free) {
		sc_mmap_addr(new_base, new_sz - old_sz, MAP_FIXED);
		sc_ret_v(regfile, (uint64_t)old_addr);
		return regfile;
	}

	/* (!is_range_free) */

	/* will have to move the block around */
	if (!(flags & MREMAP_MAYMOVE)) {
		sc_ret_v(regfile, ~0);
		return regfile;
	}

	/* get new memory, copy data, free old memory */
	new_base = kmc_alloc_aligned(new_sz, "mremap");
	for (i = 0; i < old_sz; i++)
		((char*)new_base)[i] = ((char*)old_addr)[i];
	kmc_free_run((uint64_t)old_addr, old_sz);

	sc_ret_v(regfile, (uint64_t)new_base);
	return regfile;
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

/*
However, the actual Linux system call returns
the new program break on success.  On failure, the system call returns the
current break.  The glibc wrapper function does some work (i.e., checks
whether the new break is less than addr) to provide the 0 and -1 return values
described above.
 */
void* sc_brk(void* regfile)
{
	uintptr_t	new_addr;

	/* NB: to disable brk(), just return 0 every time */

	/* setup last_brk if never set before */
	if (last_brk == 0) {
		last_brk = (void*)heap_end;
	}

/* usual first request size is 132kb, so allocate about that much */
/* we're demand paged so it shouldn't cost much anyway */
#define DEFAULT_HEAP_SZ	200*1024
	if (last_brk == 0) {
		/* hm, no heap. better make one */
		heap_begin = (uint64_t)kmc_alloc_aligned(
			DEFAULT_HEAP_SZ,
			"brk_init");
		heap_end = heap_begin + DEFAULT_HEAP_SZ;
		last_brk = (void*)heap_begin;
	}

#define EXCESSIVE_BRK_BYTES	0x70000000
	new_addr = GET_ARG0(regfile);
	if (	new_addr != 0 &&
		((intptr_t)new_addr-(intptr_t)heap_end) > EXCESSIVE_BRK_BYTES)
	{
		klee_ureport("Excessive brk", "bigbrk.err");
		/* return last seen value */
		goto done;
	}


	if (klee_is_symbolic(new_addr)) {
		klee_warning_once("concretizing brk");
		new_addr = concretize_u64(GET_ARG0(regfile));
	}

	if (new_addr != 0) {
		/* update program break to new position */
		/* XXX STUB STUB STUB */
		ptrdiff_t	new_space;
		new_space = new_addr - heap_end;
		if (new_space < 0) {
			/* we can satisfy this request-- enough space */
			last_brk = (void*)new_addr;
		}
	}

done:
	sc_ret_v(regfile, (uintptr_t)last_brk);
	return regfile;
}

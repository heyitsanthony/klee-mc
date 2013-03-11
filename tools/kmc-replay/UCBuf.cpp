#include <stdlib.h>
#include <sys/mman.h>
#include "../../runtime/mmu/uc.h"
#include "UCBuf.h"

#define PAGE_SZ	0x1000UL

UCBuf::UCBuf(
	Guest			*_gs,
	uint64_t		_in_pivot,
	unsigned		_radius,
	const std::vector<char>& _init_data)
: gs(_gs)
, radius(_radius)
, init_data(_init_data)
{
	int		err;
	uint64_t	off;
	guest_ptr	req_addr;

	off = _in_pivot & 0xfff;
	if (off != 0) {
		std::cerr << "[UCBuf] OFF = " << off << '\n';
		/* XXX: has some requried alignment */
	}

	/* XXX: this should be more precise */
	page_c = ((PAGE_SZ-1)+off+radius*2+1)/PAGE_SZ;
	std::cerr << "[UCBuf] PAGE_C = " << page_c << '\n';

	/* XXX: be smarter about alignment. Want a way
	 * to keep the same alignment but make it so accesses
	 * left or right cause a crash */
	req_addr = guest_ptr(_in_pivot & ~(PAGE_SZ - 1));
	err = gs->getMem()->mmap(
		ptr_seg_base,
		req_addr,
		page_c*PAGE_SZ,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
		-1,
		0);
	assert (err == 0);
	assert (ptr_seg_base.o == req_addr.o);

	ptr_buf_pivot.o = ptr_seg_base + off; 
	ptr_buf_base.o = ptr_buf_pivot - radius;

	/* copy in data */
	std::cerr << "[UCBuf] INIT_DATASIZE=" << init_data.size() << '\n';
	gs->getMem()->memcpy(getBase(), init_data.data(), radius*2+1);
	std::cerr << "[UCBuf] PTR = " << (void*)ptr_seg_base.o << '\n';
}

UCBuf::~UCBuf()
{
	gs->getMem()->munmap(ptr_seg_base, page_c * PAGE_SZ);
}


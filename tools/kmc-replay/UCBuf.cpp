#include <stdlib.h>
#include <sys/mman.h>
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
	guest_ptr	result;

	page_c = PAGE_SZ*((_radius*2+1 + (PAGE_SZ - 1))/PAGE_SZ);
	assert (0 ==1  && "STUB!!");
	err = gs->getMem()->mmap(result, guest_ptr(0), _radius*2+1,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE,
		-1,
		0);
	assert (err == 0);

	assert (0 == 1 && "STUB");
}

UCBuf::~UCBuf()
{
	gs->getMem()->munmap(ptr_seg_base, page_c * PAGE_SZ);
}


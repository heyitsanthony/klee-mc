#ifndef UCBUF_H
#define UCBUF_H

#include "guest.h"

class UCBuf
{
public:
	UCBuf(	Guest			*gs,
		uint64_t		in_pivot,
		unsigned		radius,
		const std::vector<char>& init_data);

	virtual ~UCBuf();
	unsigned getRadius(void) const { return radius; }

	const char* getData(void) const { return init_data.data(); }
	unsigned getDataLength(void) const { return init_data.size(); }
	unsigned getNumPages(void) const { return page_c; }

	guest_ptr getBase(void) const { return ptr_buf_base; }
	guest_ptr getPivot(void) const { return ptr_buf_pivot; }
	guest_ptr getSegBase(void) const { return ptr_seg_base; }
private:
	Guest			*gs;

	guest_ptr		ptr_seg_base;
	guest_ptr		ptr_buf_pivot;
	guest_ptr		ptr_buf_base;

	unsigned 		radius;
	unsigned		page_c;

	std::vector<char>	init_data;
};

#endif

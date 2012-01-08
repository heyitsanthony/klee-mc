#ifndef UCBUF_H
#define UCBUF_H

#include "guest.h"

class UCBuf
{
public:
	UCBuf(	const std::string& in_name,
		guest_ptr base_ptr,
		unsigned used_len,
		const std::vector<char>& init_data);
	virtual ~UCBuf() {}
	const std::string& getName() const { return name; }
	guest_ptr getBase(void) const { return base_ptr; }
	unsigned getUsedLength(void) const { return used_len; }
	unsigned getIdx(void) const { return pt_idx; }

	static unsigned getPtIdx(const std::string& s);

	const char* getData(void) const { return init_data.data(); }
	unsigned getDataLength(void) const { return init_data.size(); }

	guest_ptr getAlignedBase(void) const
	{ return guest_ptr(base_ptr.o & ~((uint64_t)0x7)); }
private:
	const std::string	name;
	guest_ptr		base_ptr;
	unsigned 		used_len;
	std::vector<char>	init_data;
	unsigned		pt_idx;
};

#endif

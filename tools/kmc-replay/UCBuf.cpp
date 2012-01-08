#include <stdlib.h>
#include "UCBuf.h"


UCBuf::UCBuf(
	const std::string& in_name,
	guest_ptr in_base_ptr,
	unsigned in_used_len,
	const std::vector<char>& in_init_data)
: name(in_name)
, base_ptr(in_base_ptr)
, used_len(in_used_len)
, init_data(in_init_data)
{
	pt_idx = getPtIdx(name);
}

unsigned UCBuf::getPtIdx(const std::string& s)
{ return atoi(s.c_str() + 7); }

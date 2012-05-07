#include "klee/Internal/ADT/Hash.h"
#include <sstream>
#include <openssl/md5.h>
#include <openssl/sha.h>

static const char i2x[] = "0123456789abcdef";

using namespace klee;

std::string Hash::MD5(const unsigned char* buf, unsigned sz)
{
	unsigned char md[16];
	std::stringstream ss;

	::MD5(buf, sz, md);
	for (unsigned i = 0; i < 16; i++)
		ss << i2x[(md[i] & 0xf0) >> 4] << i2x[md[i] & 0x0f];

	return ss.str();
}

std::string Hash::SHA(const unsigned char* buf, unsigned sz)
{
	unsigned char md[SHA_DIGEST_LENGTH];
	std::stringstream ss;

	::SHA1(buf, sz, md);
	for (unsigned i = 0; i < SHA_DIGEST_LENGTH; i++)
		ss << i2x[(md[i] & 0xf0) >> 4] << i2x[md[i] & 0x0f];

	return ss.str();
}
#ifndef KLEE_HASH_H
#define KLEE_HASH_H

#include <string>

namespace klee
{
class Hash
{
public:
	virtual ~Hash() {}
	static std::string MD5(const unsigned char* buf, unsigned sz);
	static std::string SHA(const unsigned char* buf, unsigned sz);
protected:
	Hash() {}
};
}
#endif

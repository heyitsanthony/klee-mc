#ifndef __COMMON_KTEST_STREAM_H
#define __COMMON_KTEST_STREAM_H

#include "klee/Internal/ADT/KTest.h"

namespace klee
{
class KTestStream
{
public:
	virtual ~KTestStream(void);
	static KTestStream* create(const char* file);

	const KTestObject* nextObject(void);

	const KTestObject* peekObject(void) const
	{
		if (idx >= kt->numObjects) return NULL;
		return &kt->objects[idx];
	}

	const KTest* getKTest(void) const { return kt; }
	char* feedObjData(unsigned int len = ~0U);

	const std::string& getPath(void) const { return fname; }
	KTestStream* copy(void) const;

protected:
	KTestStream(KTest* kt);
	unsigned int	idx;
	KTest		*kt;
	std::string	fname;
};

}

#endif

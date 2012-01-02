#ifndef __COMMON_KTEST_STREAM_H
#define __COMMON_KTEST_STREAM_H

#include "klee/Internal/ADT/KTest.h"

namespace klee {

class KTestStream
{
public:
	virtual ~KTestStream(void);
	static KTestStream* create(const char* file);

	const KTestObject* nextObject(void)
	{
		const KTestObject	*ret;
		ret = peekObject();
		idx++;
		return ret;
	}

	const KTestObject* peekObject(void) const
	{
		if (idx >= kt->numObjects) return NULL;
		return &kt->objects[idx];
	}

	const KTest* getKTest(void) const { return kt; }
	char* feedObjData(unsigned int len = ~0U);
protected:
	KTestStream(KTest* kt);
private:
	unsigned int	idx;
	KTest		*kt;
};

}

#endif
